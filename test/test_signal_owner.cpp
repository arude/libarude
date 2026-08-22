///
/// \file
/// \copyright Copyright 2026 Adrian Rudin (arude).
/// SPDX-License-Identifier: AGPL-3.0-or-later
///
/// libarude is dual licensed. See LICENSE for the AGPLv3 terms, and
/// LICENSING.md for the alternative MIT license available by arrangement.
///
/// The bundles below are counters rather than signals. libarude supplies no
/// signal type and never looks inside the bundle, so a counter exercises
/// everything a real one would: what is under test is which object a caller
/// reaches, and through which of the two classes.
///

#include "arude/exception.hpp"
#include "arude/signal_owner.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <type_traits>
#include <utility>

namespace arude
{

// Declared in this translation unit only, never defined and never called: the
// ADL scenario below asks whether name lookup reaches namespace arude for a
// given argument type, and nothing more.
auto adl_probe(const auto& value) -> void;

} // namespace arude

namespace signal_owner_test
{

///
/// The bundle under test, standing in for whatever signals a consumer keeps.
///
struct widget_sigs final
{
  int changed = 0;
  int closed = 0;
};

///
/// A second, unrelated bundle, so that two owners in one program can be shown
/// to keep their signals to themselves.
///
struct gadget_sigs final
{
  int changed = 0;
};

///
/// A bundle carrying no default member initialiser, so that what starts it at
/// zero can only be the value initialisation arude::signal_owner does.
///
struct raw_sigs final
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init): left uninitialised deliberately, which is the test.
  int count;
};

///
/// A bundle that cannot be copied, to show that copying an owner is the
/// bundle's decision rather than the base's.
///
struct move_only_sigs final
{
  std::unique_ptr<int> token;
};

///
/// A bundle with something to be told, which an owner has to pass down.
///
struct seeded_sigs final
{
  int changed = 0;
  int limit = 0;
};

///
/// A bundle that cannot be default constructed at all, and so can only be
/// owned through the ctor taking arguments.
///
struct required_sigs final
{
  ///
  /// Ctor.
  /// \param start What the counter starts at.
  ///
  explicit required_sigs(int start);

  int changed;
};

///
/// A bundle that will build itself out of anything it is handed.
/// It is what makes the guard on the forwarding ctor worth having: without it
/// that ctor is the better match for a non-const owner, and would build a
/// bundle out of the owner being copied.
///
struct greedy_sigs final
{
  greedy_sigs() = default;

  ///
  /// Ctor taking anything and keeping none of it.
  /// \tparam TAny Whatever it was handed.
  /// \param any The argument, which is dropped.
  ///
  template<typename TAny>
  // NOLINTNEXTLINE(bugprone-forwarding-reference-overload): hiding the copy ctor is the hazard being reproduced.
  explicit greedy_sigs([[maybe_unused]] TAny&& any);

  int changed = 0;
};

///
/// The reference owner, shaped exactly as the documentation on
/// arude::signal_owner describes.
///
class widget final : public arude::signal_owner<widget_sigs>
{
public:
  ///
  /// Announces a change through the owned bundle.
  ///
  auto change() -> void;
};

///
/// A second owner, of another bundle.
///
class gadget final : public arude::signal_owner<gadget_sigs>
{
public:
  ///
  /// Announces a change through the owned bundle.
  ///
  auto change() -> void;
};

///
/// An owner of the bundle with no initialisers.
///
class raw_widget final : public arude::signal_owner<raw_sigs>
{};

///
/// An owner of a bundle that cannot be copied.
///
class move_only_widget final : public arude::signal_owner<move_only_sigs>
{};

///
/// An owner that hands its own ctor arguments to the bundle.
///
class seeded_widget final : public arude::signal_owner<seeded_sigs>
{
public:
  ///
  /// Ctor.
  /// \param limit What the bundle's limit is set to.
  ///
  explicit seeded_widget(int limit);
};

///
/// An owner of a bundle that has to be told something, which is what the ctor
/// taking arguments is for: there is no other way to reach a private member of
/// the base.
///
class required_widget final : public arude::signal_owner<required_sigs>
{
public:
  ///
  /// Ctor.
  /// \param start What the bundle's counter starts at.
  ///
  explicit required_widget(int start);
};

///
/// A base with storage of its own.
/// Anything deriving from this and from arude::signal_owner keeps its owner
/// subobject at a non-zero offset, which is what makes the round trip through
/// the erased pointer worth measuring.
///
class preamble
{
private: // Variables
  // Never read: the member exists only to give this base a size.
  [[maybe_unused]] long long padding_ = 0;
};

///
/// An owner that inherits something else first, so its bundle is not where a
/// pointer to the whole object points.
///
class padded_widget final
  : public preamble
  , public arude::signal_owner<widget_sigs>
{};

///
/// An owner that keeps its bundle to itself.
/// A relay must refuse it: the cast in sigs() could not reach a private base,
/// and handing out what the class took care to hide is not the relay's to do.
///
class private_widget final : private arude::signal_owner<widget_sigs>
{};

///
/// An owner of two bundles at once, whose erased base is therefore ambiguous.
/// Nothing here is nonsense in itself — it announces two unrelated things — but
/// a relay cannot be bound to it, since there is no one pointer to take.
///
class twin_widget final
  : public arude::signal_owner<widget_sigs>
  , public arude::signal_owner<gadget_sigs>
{};

///
/// A class publishing the right bundle type and owning nothing.
/// The type a relay names says what it announces; it is the owner handed to the
/// ctor that has to hold it.
///
class stranger final
{
public:
  using sigs_t = widget_sigs;
};

///
/// A class deriving from the erased base directly, with the right bundle type.
/// It is what a check phrased as "derives from signal_owner_base, and its
/// bundle type matches" would let through, and the cast in sigs() would then
/// reach a bundle that was never there.
///
class impostor final : public arude::signal_owner_base
{
public:
  using sigs_t = widget_sigs;
};

///
/// The reference relay, and the harness for what the base's ctor accepts.
/// Its own ctor is constrained by the base's, so a constraint that fails is a
/// false rather than an error and a constructibility trait can read the verdict
/// off this class's signature.
///
class widget_task final : public arude::signal_relay<widget_task>
{
public: // Typedefs
  using sigs_t = widget_sigs;

public: // Constants
  ///
  /// Whether the base's ctor accepts the owner.
  /// A variable template and not a concept, because a concept may not sit in a
  /// class, and this way the ctor's constraint is a stable spelling that the
  /// definition below can repeat exactly.
  ///
  template<typename TOwner>
  static constexpr bool ctor_accepts_v = requires(TOwner& owner) { signal_relay{owner}; };

public: // Structors
  ///
  /// Ctor forwarding the owner to the base.
  /// \tparam TOwner The owner type under test.
  /// \param owner The owner to announce through.
  ///
  template<typename TOwner>
    requires widget_task::ctor_accepts_v<TOwner>
  explicit widget_task(TOwner& owner);

public: // Methods
  ///
  /// Announces a change through the owner's bundle.
  ///
  auto announce() -> void;

  ///
  /// Announces a change from a const method, which a relay allows and an owner
  /// does not: the bundle belongs to the owner, and announcing leaves the relay
  /// as it was.
  ///
  auto announce_from_const() const -> void;

  ///
  /// The bundle reached through a const relay.
  /// \return Its address, for comparison with the owner's own.
  ///
  [[nodiscard]] auto sigs_address() const -> const void*;
};

///
/// A relay naming the other bundle, so that a mismatch has something to be
/// measured against.
///
class gadget_task final : public arude::signal_relay<gadget_task>
{
public: // Typedefs
  using sigs_t = gadget_sigs;

public: // Structors
  ///
  /// Ctor forwarding the owner to the base.
  /// \param owner The owner to announce through.
  ///
  explicit gadget_task(gadget& owner);

public: // Methods
  ///
  /// Announces a change through the owner's bundle.
  ///
  auto announce() -> void;
};

///
///
required_sigs::required_sigs(const int start)
  : changed{start}
{
}

///
///
template<typename TAny>
// NOLINTNEXTLINE(bugprone-forwarding-reference-overload): hiding the copy ctor is the hazard being reproduced.
greedy_sigs::greedy_sigs([[maybe_unused]] TAny&& any)
{
}

///
///
auto widget::change() -> void
{
  ++sigs().changed;
}

///
///
auto gadget::change() -> void
{
  ++sigs().changed;
}

///
///
seeded_widget::seeded_widget(const int limit)
  : signal_owner{0, limit}
{
}

///
///
required_widget::required_widget(const int start)
  : signal_owner{start}
{
}

///
///
template<typename TOwner>
  requires widget_task::ctor_accepts_v<TOwner>
widget_task::widget_task(TOwner& owner)
  : signal_relay{owner}
{
}

///
///
auto widget_task::announce() -> void
{
  ++sigs().changed;
}

///
///
auto widget_task::announce_from_const() const -> void
{
  ++sigs().changed;
}

///
///
auto widget_task::sigs_address() const -> const void*
{
  return &sigs();
}

///
///
gadget_task::gadget_task(gadget& owner)
  : signal_relay{owner}
{
}

///
///
auto gadget_task::announce() -> void
{
  ++sigs().changed;
}

// Only ADL can reach arude::adl_probe from here: this namespace encloses no
// declaration of that name, so an unqualified call succeeds exactly when the
// argument's type drags namespace arude in with it.
template<typename T>
concept adl_reaches_arude_c = requires(const T& value) { adl_probe(value); };

} // namespace signal_owner_test

SCENARIO("the erased base is nothing but an anchor", "[signal_owner]")
{
  GIVEN("the type on its own")
  {
    THEN("a caller can neither create nor destroy one")
    {
      // The structors are protected, so an owner cannot be deleted through a
      // pointer to this base, and nothing outside the hierarchy can hold one by
      // value.
      STATIC_REQUIRE(!std::is_destructible_v<arude::signal_owner_base>);
      STATIC_REQUIRE(!std::is_default_constructible_v<arude::signal_owner_base>);
    }

    THEN("it carries no vtable, since nothing here is meant to be virtual")
    {
      STATIC_REQUIRE(!std::is_polymorphic_v<arude::signal_owner_base>);
      STATIC_REQUIRE(!std::has_virtual_destructor_v<arude::signal_owner_base>);
    }

    THEN("it is empty, so an owner costs no more than the bundle it keeps")
    {
      STATIC_REQUIRE(std::is_empty_v<arude::signal_owner_base>);
      STATIC_REQUIRE(sizeof(signal_owner_test::widget) == sizeof(signal_owner_test::widget_sigs));
    }
  }
}

SCENARIO("an owner keeps its bundle and hands it out", "[signal_owner]")
{
  GIVEN("an owner")
  {
    auto widget = signal_owner_test::widget{};

    THEN("it publishes the bundle type and derives from the erased base")
    {
      STATIC_REQUIRE(std::is_same_v<signal_owner_test::widget::sigs_t, signal_owner_test::widget_sigs>);
      STATIC_REQUIRE(std::is_base_of_v<arude::signal_owner_base, signal_owner_test::widget>);
    }

    THEN("sigs() names one object, not a copy of one")
    {
      REQUIRE(&widget.sigs() == &widget.sigs());
    }

    WHEN("it announces")
    {
      widget.change();
      widget.change();

      THEN("the bundle kept the state")
      {
        REQUIRE(widget.sigs().changed == 2);
        REQUIRE(widget.sigs().closed == 0);
      }
    }
  }

  GIVEN("an owner whose bundle carries no default member initialiser")
  {
    const auto raw = signal_owner_test::raw_widget{};

    THEN("the bundle was value initialised rather than left indeterminate")
    {
      REQUIRE(raw.sigs().count == 0);
    }
  }
}

// Nothing in arude::signal_owner is declared, so the bundle decides what an
// owner can do. An owner that had lost the moves because the base declared a
// dtor would be a trap, and this is what would catch it.
SCENARIO("what an owner can be done to is what its bundle can be done to", "[signal_owner]")
{
  GIVEN("an owner of a copyable bundle")
  {
    THEN("it can be copied and moved")
    {
      STATIC_REQUIRE(std::is_copy_constructible_v<signal_owner_test::widget>);
      STATIC_REQUIRE(std::is_copy_assignable_v<signal_owner_test::widget>);
      STATIC_REQUIRE(std::is_move_constructible_v<signal_owner_test::widget>);
      STATIC_REQUIRE(std::is_move_assignable_v<signal_owner_test::widget>);
    }

    WHEN("one is copied")
    {
      auto source = signal_owner_test::widget{};
      source.change();
      auto copy = source;

      THEN("the copy carries the state and keeps its own bundle")
      {
        REQUIRE(copy.sigs().changed == 1);
        REQUIRE(&copy.sigs() != &source.sigs());
      }
    }
  }

  GIVEN("an owner of a bundle that cannot be copied")
  {
    THEN("the owner cannot be copied either, and can still be moved")
    {
      STATIC_REQUIRE(!std::is_copy_constructible_v<signal_owner_test::move_only_widget>);
      STATIC_REQUIRE(!std::is_copy_assignable_v<signal_owner_test::move_only_widget>);
      STATIC_REQUIRE(std::is_move_constructible_v<signal_owner_test::move_only_widget>);
      STATIC_REQUIRE(std::is_move_assignable_v<signal_owner_test::move_only_widget>);
    }
  }
}

SCENARIO("a relay announces through its owner's bundle", "[signal_owner]")
{
  GIVEN("an owner and a relay bound to it")
  {
    auto widget = signal_owner_test::widget{};
    auto task = signal_owner_test::widget_task{widget};

    THEN("both reach the same bundle")
    {
      REQUIRE(&task.sigs() == &widget.sigs());
    }

    THEN("the relay costs one pointer and nothing else")
    {
      STATIC_REQUIRE(sizeof(signal_owner_test::widget_task) == sizeof(void*));
    }

    // What the owner being held as a pointer rather than a reference buys. A
    // reference member would delete both assignment operators, and the relay is
    // a base rather than a member, so every relaying class would lose them with
    // it. Asked here so that spelling the member as a reference fails the build
    // rather than the documentation.
    THEN("the relay can be assigned as well as copied")
    {
      STATIC_REQUIRE(std::is_copy_constructible_v<signal_owner_test::widget_task>);
      STATIC_REQUIRE(std::is_copy_assignable_v<signal_owner_test::widget_task>);
      STATIC_REQUIRE(std::is_move_constructible_v<signal_owner_test::widget_task>);
      STATIC_REQUIRE(std::is_move_assignable_v<signal_owner_test::widget_task>);
    }

    WHEN("the relay announces")
    {
      task.announce();

      THEN("what the owner announces through carries it")
      {
        REQUIRE(widget.sigs().changed == 1);
      }
    }

    WHEN("the owner announces")
    {
      widget.change();

      THEN("the relay sees it, since there is only the one bundle")
      {
        REQUIRE(task.sigs().changed == 1);
      }
    }
  }

  // A relay is a non-owning reference, so a copy of one refers to the same
  // owner rather than to a bundle of its own.
  GIVEN("a relay and a copy of it")
  {
    auto widget = signal_owner_test::widget{};
    auto task = signal_owner_test::widget_task{widget};
    auto copy = task;

    WHEN("the copy announces")
    {
      copy.announce();

      THEN("it went to the same owner")
      {
        REQUIRE(widget.sigs().changed == 1);
        REQUIRE(&copy.sigs() == &widget.sigs());
      }
    }
  }

  // Assignment is the other half of what a reference member would have cost:
  // rebinding. A relay bound to one owner can be pointed at another afterwards.
  GIVEN("a relay bound to the first of two owners")
  {
    auto first = signal_owner_test::widget{};
    auto second = signal_owner_test::widget{};
    auto task = signal_owner_test::widget_task{first};

    WHEN("it is assigned a relay bound to the second")
    {
      task = signal_owner_test::widget_task{second};
      task.announce();

      THEN("it announces through the second owner and leaves the first as it was")
      {
        REQUIRE(&task.sigs() == &second.sigs());
        REQUIRE(second.sigs().changed == 1);
        REQUIRE(first.sigs().changed == 0);
      }
    }
  }
}

// The stored pointer is an arude::signal_owner_base*, and the bundle only comes
// back at the cast in sigs(). An owner whose bundle sits behind another base is
// what shows the cast adjusting the address rather than handing back what it
// was given.
SCENARIO("the erased pointer finds the bundle whatever the owner's layout", "[signal_owner]")
{
  GIVEN("an owner whose bundle sits behind another base")
  {
    auto padded = signal_owner_test::padded_widget{};
    auto task = signal_owner_test::widget_task{padded};

    THEN("the owner subobject is off offset zero, so the cast is doing work")
    {
      const auto* const owner = static_cast<const arude::signal_owner<signal_owner_test::widget_sigs>*>(&padded);
      REQUIRE(static_cast<const void*>(owner) != static_cast<const void*>(&padded));
    }

    THEN("the relay still lands on the owner's bundle")
    {
      REQUIRE(&task.sigs() == &padded.sigs());
    }
  }
}

// What makes the downcast in sigs() defined is that the ctor took an owner it
// can cast back to. The constraint carries that, so every rejection below is a
// false rather than a compile error, and the acceptance is not merely asserted
// but observed through a trait.
SCENARIO("a relay takes only an owner of the bundle it names", "[signal_owner]")
{
  GIVEN("a relay naming a bundle")
  {
    THEN("an owner of that bundle is accepted")
    {
      STATIC_REQUIRE(std::is_constructible_v<signal_owner_test::widget_task, signal_owner_test::widget&>);
    }

    THEN("so is one that keeps its bundle behind another base")
    {
      STATIC_REQUIRE(std::is_constructible_v<signal_owner_test::widget_task, signal_owner_test::padded_widget&>);
    }

    THEN("an owner of another bundle is rejected")
    {
      STATIC_REQUIRE_FALSE(std::is_constructible_v<signal_owner_test::widget_task, signal_owner_test::gadget&>);
    }

    THEN("a class that owns nothing is rejected, whatever type it publishes")
    {
      STATIC_REQUIRE_FALSE(std::is_constructible_v<signal_owner_test::widget_task, signal_owner_test::stranger&>);
    }

    // The interesting one: it satisfies everything a check phrased as "derives
    // from the erased base, and the bundle types match" would ask, and the cast
    // would still land on a bundle that is not there.
    THEN("so is a class deriving from the erased base without being an owner")
    {
      STATIC_REQUIRE_FALSE(std::is_constructible_v<signal_owner_test::widget_task, signal_owner_test::impostor&>);
    }

    THEN("so is one whose owner base is private, which the cast could not reach")
    {
      STATIC_REQUIRE_FALSE(std::is_constructible_v<signal_owner_test::widget_task, signal_owner_test::private_widget&>);
    }

    THEN("so is one owning two bundles, whose erased base is ambiguous")
    {
      STATIC_REQUIRE_FALSE(std::is_constructible_v<signal_owner_test::widget_task, signal_owner_test::twin_widget&>);
    }
  }
}

// An owner keeps its bundle, so const reaches it. A relay only refers to one,
// exactly as a pointer member would, so its own constness says nothing about
// what it points at.
SCENARIO("const reaches the bundle through the owner and not through the relay", "[signal_owner]")
{
  GIVEN("an owner and a const reference to it")
  {
    THEN("each reaches the matching overload")
    {
      STATIC_REQUIRE(
        std::is_same_v<decltype(std::declval<signal_owner_test::widget&>().sigs()), signal_owner_test::widget_sigs&>);
      STATIC_REQUIRE(
        std::is_same_v<
          decltype(std::declval<const signal_owner_test::widget&>().sigs()),
          const signal_owner_test::widget_sigs&>);
    }
  }

  GIVEN("a relay and a const reference to it")
  {
    THEN("there is the one accessor, and the bundle it hands back is not const")
    {
      STATIC_REQUIRE(
        std::
          is_same_v<decltype(std::declval<signal_owner_test::widget_task&>().sigs()), signal_owner_test::widget_sigs&>);
      STATIC_REQUIRE(
        std::is_same_v<
          decltype(std::declval<const signal_owner_test::widget_task&>().sigs()),
          signal_owner_test::widget_sigs&>);
    }
  }

  GIVEN("an owner and a const relay bound to it")
  {
    auto widget = signal_owner_test::widget{};
    const auto task = signal_owner_test::widget_task{widget};

    THEN("the const relay still names the owner's bundle")
    {
      REQUIRE(task.sigs_address() == &widget.sigs());
    }

    WHEN("the const relay announces")
    {
      task.announce_from_const();

      THEN("it reached the owner's bundle, which was never const for it")
      {
        REQUIRE(widget.sigs().changed == 1);
      }
    }
  }
}

// The bundle is private, so an owner that starts it as anything but value
// initialised reaches it through the ctor taking arguments, and a bundle that
// cannot be default constructed can be owned no other way.
SCENARIO("an owner can build its bundle rather than default it", "[signal_owner]")
{
  GIVEN("an owner handing its own ctor arguments down to the bundle")
  {
    const auto seeded = signal_owner_test::seeded_widget{7};

    THEN("the bundle was built from them")
    {
      REQUIRE(seeded.sigs().limit == 7);
      REQUIRE(seeded.sigs().changed == 0);
    }
  }

  GIVEN("a bundle that cannot be default constructed")
  {
    THEN("neither can an owner of it, which is an answer rather than an error")
    {
      STATIC_REQUIRE_FALSE(std::is_default_constructible_v<arude::signal_owner<signal_owner_test::required_sigs>>);
      STATIC_REQUIRE_FALSE(std::is_default_constructible_v<signal_owner_test::required_widget>);
    }

    THEN("it can still be owned, since the ctor taking arguments builds it")
    {
      const auto required = signal_owner_test::required_widget{5};
      REQUIRE(required.sigs().changed == 5);
    }
  }

  GIVEN("an aggregate bundle")
  {
    THEN("the arguments are braced, so a narrowing conversion is turned away")
    {
      STATIC_REQUIRE(std::is_constructible_v<arude::signal_owner<signal_owner_test::seeded_sigs>, int, int>);
      STATIC_REQUIRE_FALSE(std::is_constructible_v<arude::signal_owner<signal_owner_test::seeded_sigs>, double, int>);
    }
  }

  // The forwarding ctor takes a non-const lvalue better than the copy ctor
  // does, so without the guard on it this is where an owner would be copied by
  // building a bundle out of it.
  GIVEN("an owner whose bundle would build itself out of anything")
  {
    auto source = arude::signal_owner<signal_owner_test::greedy_sigs>{};
    source.sigs().changed = 7;

    WHEN("it is copied where the forwarding ctor is a candidate too")
    {
      const auto copy = arude::signal_owner<signal_owner_test::greedy_sigs>{source};

      THEN("the copy ctor is what ran")
      {
        REQUIRE(copy.sigs().changed == 7);
      }
    }
  }
}

SCENARIO("two owners in one program keep their signals apart", "[signal_owner]")
{
  GIVEN("two owners of different bundles, each with a relay")
  {
    auto widget = signal_owner_test::widget{};
    auto gadget = signal_owner_test::gadget{};
    auto widget_task = signal_owner_test::widget_task{widget};
    auto gadget_task = signal_owner_test::gadget_task{gadget};

    WHEN("each relay announces")
    {
      widget_task.announce();
      gadget_task.announce();
      gadget_task.announce();

      THEN("neither reached the other's bundle")
      {
        REQUIRE(widget.sigs().changed == 1);
        REQUIRE(gadget.sigs().changed == 2);
      }
    }
  }
}

// The classes sit in their own namespace so that deriving from one does not
// make every unqualified call on an owner or a relay search namespace arude.
// Without that, an arude overload could be picked up by client code that never
// asked for one.
SCENARIO("deriving from these does not drag namespace arude into ADL", "[signal_owner]")
{
  GIVEN("a type declared in namespace arude, as a control")
  {
    THEN("ADL reaches the namespace, so the probe is capable of finding a name there")
    {
      STATIC_REQUIRE(signal_owner_test::adl_reaches_arude_c<arude::exception_base>);
    }
  }

  GIVEN("an owner and a relay")
  {
    THEN("ADL reaches namespace arude through neither")
    {
      STATIC_REQUIRE(!signal_owner_test::adl_reaches_arude_c<signal_owner_test::widget>);
      STATIC_REQUIRE(!signal_owner_test::adl_reaches_arude_c<signal_owner_test::widget_task>);
    }
  }
}

// The arude names are the spelling consumers write, and the classes themselves
// sit one namespace further down. Renaming that namespace is invisible to them,
// but it is what the ADL scenario above rests on, so the two are pinned
// together.
SCENARIO("the published names denote the classes in the nested namespace", "[signal_owner]")
{
  GIVEN("the aliases in arude and the classes they name")
  {
    THEN("they are the same types")
    {
      STATIC_REQUIRE(std::is_same_v<arude::signal_owner_base, arude::signal_owner_::signal_owner_base>);
      STATIC_REQUIRE(
        std::is_same_v<
          arude::signal_owner<signal_owner_test::widget_sigs>,
          arude::signal_owner_::signal_owner<signal_owner_test::widget_sigs>>);
      STATIC_REQUIRE(
        std::is_same_v<
          arude::signal_relay<signal_owner_test::widget_task>,
          arude::signal_owner_::signal_relay<signal_owner_test::widget_task>>);
    }
  }
}
